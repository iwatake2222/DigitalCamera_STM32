/*
 * liveviewCtrl.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "ff.h"
#include "jpeglib.h"
#include "common.h"
#include "commonMsg.h"
#include "applicationSettings.h"
#include "../hal/display.h"
#include "../hal/camera.h"


/*** Internal Const Values, Macros ***/
#define LOG(str, ...) printf("[LV_CTRL:%d] " str, __LINE__, ##__VA_ARGS__);
#define LOG_E(str, ...) printf("[LV_CTRL_ERR:%d] " str, __LINE__, ##__VA_ARGS__);

// need to modify jdatasrc.c
#define INPUT_BUF_SIZE  512  /* choose an efficiently fread'able size */

typedef enum {
  INACTIVE,
  ACTIVE,
  SINGLE_CAPTURING,
  MOVIE_RECORDING,
} STATUS;

/*** Internal Static Variables ***/
/* for status control */
static STATUS s_status = INACTIVE;
static uint8_t s_requestStopMovie = 0;  // movie record will stop at next frame

/* for encode */
static uint8_t *sp_lineBuffRGB888;
static FATFS   *sp_fatFs;
static FIL     *sp_fil;
static struct jpeg_compress_struct *sp_cinfo;
static struct jpeg_error_mgr       *sp_jerr;
static JSAMPROW s_jsamprow[2] = {0};
static uint32_t s_jpegQuality = JPEG_QUALITY;

/* for movie recording */
static uint8_t s_nextFrameReady = 0;
static uint32_t s_lastFrameStartTimeMSec = 0;

/*** Internal Function Declarations ***/
static void liveviewCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret);
static void liveviewCtrl_processMsg(MSG_STRUCT *p_msg);
static void liveviewCtrl_processFrame();
static RET liveviewCtrl_init();
static RET liveviewCtrl_exit();
static RET liveviewCtrl_startLiveView();
static RET liveviewCtrl_stopLiveView();
static RET liveviewCtrl_capture();
static RET liveviewCtrl_movieRecordStart(); // call this when start movie recording
static RET liveviewCtrl_movieRecordFinish();  // call this when stop movie recording
static RET liveviewCtrl_movieRecordFrame(); // call this every frame during movie recording

static RET liveviewCtrl_encodeJpegFrame();  // call this between liveviewCtrl_writeFileStart and liveviewCtrl_writeFilefinish
static RET liveviewCtrl_writeFileStart(char* filename);
static RET liveviewCtrl_writeFileFinish();
static RET liveviewCtrl_generateFilename(char* filename, uint8_t numPos);
static void liveviewCtrl_libjpeg_output_message (j_common_ptr cinfo);

static void liveviewCtrl_cbVsync(uint32_t frame);
static void liveviewCtrl_changeJpegQuality(int32_t delta);

/*** External Function Defines ***/
void liveviewCtrl_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(LIVEVIEW_CTRL);

  while(1) {
    osEvent event;
    event = osMessageGet(myQueueId, 10);
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
  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // receiver must free
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
    case COMMAND_COMP(CMD_REGISTER):
    case COMMAND_COMP(CMD_UNREGISTER):
      // do nothing
      break;
    default:
      LOG_E("status error\n");
      break;
    }
    break;

  case ACTIVE:
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
      LOG("input: %d %d\n", p_msg->param.input.type, p_msg->param.input.param);
      if(p_msg->param.input.type == INPUT_TYPE_KEY_CAP) {
        s_status = SINGLE_CAPTURING;
        liveviewCtrl_capture();
        s_status = ACTIVE;
      } else if(p_msg->param.input.type == INPUT_TYPE_KEY_OTHER0) {
        if(liveviewCtrl_movieRecordStart() == RET_OK){
          s_status = MOVIE_RECORDING;
        }
      } else if(p_msg->param.input.type == INPUT_TYPE_DIAL0) {
        liveviewCtrl_changeJpegQuality(p_msg->param.input.param);
      }
      break;
    case COMMAND_COMP(CMD_REGISTER):
    case COMMAND_COMP(CMD_UNREGISTER):
      // do nothing
      break;
    default:
      LOG_E("status error\n");
      break;
    }
    break;

  case SINGLE_CAPTURING:
    // there shouldn't be any message during this status
    LOG_E("status error\n");
    break;

  case MOVIE_RECORDING:
    switch(p_msg->command){
    case CMD_START:
    case CMD_STOP:
      LOG("ignored mode change\n");
      liveviewCtrl_sendComp(p_msg, RET_DO_NOTHING);
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

  default:
    LOG_E("status error\n");
    break;
  }
}

static void liveviewCtrl_processFrame()
{
  RET ret;
  if( s_status == MOVIE_RECORDING ){
    if(s_requestStopMovie) {
      liveviewCtrl_movieRecordFinish();
      s_requestStopMovie = 0;
      s_status = ACTIVE;
    } else {
      ret = liveviewCtrl_movieRecordFrame();
      if(ret != RET_OK) {
        LOG_E("error during movie rec: %08X\n", ret);
        s_requestStopMovie = 1; // stop by myself
      }
    }
  } else {
    // do nothing
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
  p_sendMsg->param.input.type    = INPUT_TYPE_DIAL0;
  p_sendMsg->param.input.param = 4; // notify every 10 ticks;
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

  if(ret != RET_OK) LOG_E("%08X\n", ret);

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

  if(ret != RET_OK) LOG_E("%08X\n", ret);

  return ret;
}

static RET liveviewCtrl_startLiveView()
{
  RET ret = RET_OK;
  void* displayHandle = display_getDisplayHandle();
  camera_stopCap();
  display_setArea(0, 0, IMAGE_SIZE_WIDTH - 1, IMAGE_SIZE_HEIGHT - 1);
  ret |= camera_startCap(CAMERA_CAP_CONTINUOUS, displayHandle);
  return ret;
}

static RET liveviewCtrl_stopLiveView()
{
  RET ret = RET_OK;
  ret |= camera_stopCap();
  return ret;
}

static RET liveviewCtrl_capture()
{
  LOG("Single Capture Start\n");
  RET ret = RET_OK;
  char filename[14] = FILENAME_JPEG;
  uint32_t start = HAL_GetTick();

  ret |= liveviewCtrl_stopLiveView();
  ret |= liveviewCtrl_generateFilename(filename, FILENAME_NUM_POS);
  LOG("create %s\n", filename);
  ret |= liveviewCtrl_writeFileStart(filename);
  ret |= liveviewCtrl_encodeJpegFrame();
  ret |= liveviewCtrl_writeFileFinish();

  LOG("encode time = %d\n", HAL_GetTick() - start);
  LOG("Single Capture Finish\n");

#if BLACK_CURTAIN_TIME > 0
  display_drawRect(0, 0, IMAGE_SIZE_WIDTH, IMAGE_SIZE_HEIGHT, DISPLAY_COLOR_BLACK);  // like shutter
  HAL_Delay(BLACK_CURTAIN_TIME);
#endif

  /*** restart liveview ***/
  ret |= liveviewCtrl_startLiveView();

  if(ret != RET_OK) LOG_E("%d\n", ret);
  return ret;
}

static RET liveviewCtrl_movieRecordStart()
{
  LOG("Movie Record Start\n");
  RET ret = RET_OK;
  char filename[14] = FILENAME_MOVIE;
  ret |= liveviewCtrl_stopLiveView();

  s_nextFrameReady = 1; // the first frame is always ready because I can reuse liveview image
  s_lastFrameStartTimeMSec = HAL_GetTick();

  ret |= liveviewCtrl_generateFilename(filename, FILENAME_NUM_POS);
  LOG("create %s\n", filename);
  ret |= liveviewCtrl_writeFileStart(filename);
  if(ret != RET_OK) {
    ret |= liveviewCtrl_writeFileFinish();
    LOG_E("Movie Record End by error: %08X\n", ret);
    return ret;
  }

  camera_registerCallback(0, liveviewCtrl_cbVsync);

  return ret;
}

static RET liveviewCtrl_movieRecordFinish()
{
  RET ret = RET_OK;

  camera_registerCallback(0, 0);
  ret |= liveviewCtrl_writeFileFinish();

#if BLACK_CURTAIN_TIME > 0
  camera_stopCap();   // the last frame which was not recorded might running and pixel data might be being transfered to Display
  display_drawRect(0, 0, IMAGE_SIZE_WIDTH, IMAGE_SIZE_HEIGHT, DISPLAY_COLOR_BLACK);  // like shutter
  HAL_Delay(BLACK_CURTAIN_TIME);
#endif

  ret |= liveviewCtrl_startLiveView();
  if(ret != RET_OK) {
    LOG_E("Movie Encode End by error: %08X\n", ret);
  }
  LOG("Movie Record Finish\n");

  return ret;
}

static RET liveviewCtrl_movieRecordFrame()
{
  RET ret = RET_OK;

  if(s_nextFrameReady) {
    if(HAL_GetTick() - s_lastFrameStartTimeMSec > MOTION_JPEG_FPS_MSEC) { // control fps
      LOG("Movie One Frame Encode. Current FPS(msec) = %d\n", HAL_GetTick() - s_lastFrameStartTimeMSec);
      s_lastFrameStartTimeMSec = HAL_GetTick();
      /* encode one frame (do not close file yet) */
      ret |= liveviewCtrl_encodeJpegFrame();
      /* capture next frame */
      void* displayHandle = display_getDisplayHandle();
      display_setArea(0, 0, IMAGE_SIZE_WIDTH - 1, IMAGE_SIZE_HEIGHT - 1);
      ret |= camera_startCap(CAMERA_CAP_SINGLE_FRAME, displayHandle);
      s_nextFrameReady = 0;
    } else {
      // skip for fps control
    }
  } else {
    /* not ready (copying image data from camera to display) */

    // workaround. Vsync signal sometimes doesn't come (probably because of poor hardware work)
    if(HAL_GetTick() - s_lastFrameStartTimeMSec > MOTION_JPEG_FPS_MSEC*3) {
      LOG_E("frame lost\n");
      s_nextFrameReady = 1;
    }
  }

  return ret;
}

static void liveviewCtrl_cbVsync(uint32_t frame)
{
  camera_stopCap();
  s_nextFrameReady = 1;
}

static RET liveviewCtrl_encodeJpegFrame()
{
  RET ret = RET_OK;

  /*** alloc memory ***/
  sp_cinfo = pvPortMalloc(sizeof(struct jpeg_compress_struct));
  sp_jerr  = pvPortMalloc(sizeof(struct jpeg_error_mgr));
  sp_lineBuffRGB888 = pvPortMalloc(IMAGE_SIZE_WIDTH * 3);

  if( (sp_cinfo == 0) || (sp_jerr == 0) || (sp_lineBuffRGB888 == 0) ){
    LOG_E("not enough memory\n");
    vPortFree(sp_cinfo);
    vPortFree(sp_jerr);
    vPortFree(sp_lineBuffRGB888);
    return RET_ERR_MEMORY;
  }

  /*** prepare libjpeg ***/
  s_jsamprow[0] = sp_lineBuffRGB888;
  sp_cinfo->err = jpeg_std_error(sp_jerr);
  sp_cinfo->err->output_message = liveviewCtrl_libjpeg_output_message;  // over-write error output function
  jpeg_create_compress(sp_cinfo);
  jpeg_stdio_dest(sp_cinfo, sp_fil);

  /* jpeg encode setting */
  sp_cinfo->image_width  = IMAGE_SIZE_WIDTH;
  sp_cinfo->image_height = IMAGE_SIZE_HEIGHT;
  sp_cinfo->input_components = 3;
  sp_cinfo->in_color_space = JCS_RGB;
  jpeg_set_defaults(sp_cinfo);
  jpeg_set_quality(sp_cinfo, s_jpegQuality, TRUE);
  jpeg_start_compress(sp_cinfo, TRUE);

  /*** read pixel data from display and encode line by line ***/
  display_setAreaRead(0, 0, IMAGE_SIZE_WIDTH - 1, IMAGE_SIZE_HEIGHT - 1);
  for(uint32_t y = 0; y < IMAGE_SIZE_HEIGHT; y++) {
    /* read one line from display device (as an external RAM) */
    display_readImageRGB888(sp_lineBuffRGB888, IMAGE_SIZE_WIDTH);
    /* encode one line */
    if(jpeg_write_scanlines(sp_cinfo, s_jsamprow, 1) != 1) {
      LOG_E("Single Encode Stop at line %d\n", y);
      break;
    }
  }

  /*** finalize libjpeg ***/
  jpeg_finish_compress(sp_cinfo);
  jpeg_destroy_compress(sp_cinfo);

  /*** free memory ***/
  vPortFree(sp_cinfo);
  vPortFree(sp_jerr);
  vPortFree(sp_lineBuffRGB888);

  return ret;
}

static RET liveviewCtrl_writeFileStart(char* filename)
{
  RET ret = RET_OK;
  sp_fatFs = pvPortMalloc(sizeof(FATFS));
  sp_fil   = pvPortMalloc(sizeof(FIL));
  if( (sp_fatFs == 0) || (sp_fil == 0) ) {
    return RET_ERR_MEMORY;
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

static RET liveviewCtrl_generateFilename(char* filename, uint8_t numPos)
{
  static uint8_t s_number = 0;

  FRESULT ret;
  FATFS *p_fatFs = pvPortMalloc(sizeof(FATFS));
  FIL   *p_fil   = pvPortMalloc(sizeof(FIL));
  if( (p_fatFs == 0) || (p_fil == 0) ) {
    vPortFree(p_fil);
    vPortFree(p_fatFs);
    return RET_ERR_MEMORY;
  }

  f_mount(p_fatFs, "", 0);
  do {
    uint8_t num100 = (s_number/100) % 10;
    uint8_t num10  = (s_number/10) % 10;
    uint8_t num1   = (s_number/1) % 10;
    filename[numPos + 0] = '0' + num100;
    filename[numPos + 1] = '0' + num10;
    filename[numPos + 2] = '0' + num1;
    s_number++;
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

void display_readImageRGB565(uint8_t *p_buff, uint32_t pixelNum)
{
  /* can I use DMA for this? */
  volatile uint16_t* p_lcdAddr = (volatile uint16_t* )(lcdIli9341_getDrawAddress());
  for(uint32_t x = 0; x < pixelNum / 2; x++){
    uint16_t data0 = *p_lcdAddr;
    uint16_t data1 = *p_lcdAddr;
    uint16_t data2 = *p_lcdAddr;
    uint8_t r0 = data0 >> 8;
    uint8_t g0 = data0 & 0x00FF;
    uint8_t b0 = data1 >> 8;
    uint8_t r1 = data1 & 0x00FF;
    uint8_t g1 = data2 >> 8;
    uint8_t b1 = data2 & 0x00FF;
    p_buff[x*4 + 0] = convRGB565(r0, g0, b0) >> 8;
    p_buff[x*4 + 1] = convRGB565(r0, g0, b0) & 0x00FF;
    p_buff[x*4 + 2] = convRGB565(r1, g1, b1) >> 8;
    p_buff[x*4 + 3] = convRGB565(r1, g1, b1) & 0x00FF;
  }
}

static RET liveviewCtrl_encodeRGB565Frame()
{
  uint32_t num;
  uint8_t *sp_lineBuffRGB565 = pvPortMalloc(IMAGE_SIZE_WIDTH * 2);
  display_setAreaRead(0, 0, IMAGE_SIZE_WIDTH - 1, IMAGE_SIZE_HEIGHT - 1);
  for(uint32_t y = 0; y < IMAGE_SIZE_HEIGHT; y++) {
    /* read one line from display device (as an external RAM) */
    display_readImageRGB565(sp_lineBuffRGB565, IMAGE_SIZE_WIDTH);
    f_write(sp_fil, sp_lineBuffRGB565, IMAGE_SIZE_WIDTH*2, &num);
  }

  vPortFree(sp_lineBuffRGB565);
  return RET_OK;
}

static void liveviewCtrl_libjpeg_output_message (j_common_ptr cinfo)
{
  char buffer[JMSG_LENGTH_MAX];
  /* Create the message */
  (*cinfo->err->format_message) (cinfo, buffer);
  printf( "%s\n", buffer);
}

static void liveviewCtrl_changeJpegQuality(int32_t delta)
{
  s_jpegQuality += (delta * 10);
  if(s_jpegQuality > 100) s_jpegQuality = 100;
  if(s_jpegQuality < 1) s_jpegQuality = 1;
  LOG("Q = %d\n", s_jpegQuality);
  liveviewCtrl_stopLiveView();
  display_osdBar(s_jpegQuality);
  HAL_Delay(300);
  liveviewCtrl_startLiveView();

}

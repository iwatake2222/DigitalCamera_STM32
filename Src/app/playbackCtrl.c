/*
 * playbackCtrl.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "jpeglib.h"
#include "common.h"
#include "commonHigh.h"
#include "../hal/display.h"
#include "../service/file.h"

/*** Internal Const Values, Macros ***/
#define LOG(str, ...) printf("[PB_CTRL:%d] " str, __LINE__, ##__VA_ARGS__);
#define LOG_E(str, ...) printf("[PB_CTRL_ERR:%d] " str, __LINE__, ##__VA_ARGS__);
#define PLAY_SIZE_WIDTH  320
#define PLAY_SIZE_HEIGHT 240

// need to modify jdatasrc.c
#define INPUT_BUF_SIZE  512  /* choose an efficiently fread'able size */

typedef enum {
  INACTIVE,
  ACTIVE,
} STATUS;

/*** Internal Static Variables ***/
static STATUS s_status = INACTIVE;

/*** Internal Function Declarations ***/
static void playbackCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret);
static void playbackCtrl_procInactive(MSG_STRUCT *p_msg);
static void playbackCtrl_procActive(MSG_STRUCT *p_msg);
static RET playbackCtrl_init();
static RET playbackCtrl_exit();
static RET playbackCtrl_playNext();
static RET playbackCtrl_isFileJPEG(char *filename);
static RET playbackCtrl_isFileRGB565(char *filename);
static RET playbackCtrl_playRGB565(char* filename);
static RET playbackCtrl_playJPEG(char* filename);
static RET playbackCtrl_decodeJpeg(FIL *file, uint32_t maxWidth, uint32_t maxHeight, uint8_t * pLineBuffRGB888);
static void playbackCtrl_libjpeg_output_message (j_common_ptr cinfo);
static void playbackCtrl_drawRGB888 (uint8_t* rgb888, uint32_t width);
static RET playbackCtrl_calcJpegOutputSize(struct jpeg_decompress_struct* pCinfo, uint32_t maxWidth, uint32_t maxHeight);

/*** External Function Defines ***/
void playbackCtrl_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(PLAYBACK_CTRL);

  while(1) {
    osEvent event = osMessageGet(myQueueId, osWaitForever);
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
//      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
      switch(s_status) {
      case INACTIVE:
        playbackCtrl_procInactive(p_recvMsg);
        break;
      case ACTIVE:
        playbackCtrl_procActive(p_recvMsg);
        break;
      }
      freeMemoryPoolMessage(p_recvMsg);
    }
  }
}

/*** Internal Function Defines ***/
static void playbackCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret)
{
  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->command = COMMAND_COMP(p_recvMmsg->command);
  p_sendMsg->param.val = ret;
  osMessagePut(getQueueId(p_recvMmsg->sender), (uint32_t)p_sendMsg, osWaitForever);
}

static void playbackCtrl_procInactive(MSG_STRUCT *p_msg)
{
  if (IS_COMMAND_COMP(p_msg->command)) {
    // do nothing when comp (may be comp from input)
    return;
  }

  RET ret;
  switch(p_msg->command){
  case CMD_START:
    ret = playbackCtrl_init();
    if(ret == RET_OK) s_status = ACTIVE;
    playbackCtrl_sendComp(p_msg, ret);
    break;
  case CMD_STOP:
    LOG_E("status error\n");
    playbackCtrl_sendComp(p_msg, RET_ERR_STATUS);
    break;
  case CMD_NOTIFY_INPUT:
    LOG_E("status error\n");
    break;
  }
}

static void playbackCtrl_procActive(MSG_STRUCT *p_msg)
{
  if (IS_COMMAND_COMP(p_msg->command)) {
    // do nothing when comp (may be comp from input)
    return;
  }

  RET ret;
  switch(p_msg->command){
  case CMD_START:
    LOG_E("status error\n");
    playbackCtrl_sendComp(p_msg, RET_ERR_STATUS);
    break;
  case CMD_STOP:
    ret = playbackCtrl_exit();
    if(ret == RET_OK) s_status = INACTIVE;
    playbackCtrl_sendComp(p_msg, ret);
    break;
  case CMD_NOTIFY_INPUT:
    LOG("input: %d %d\n", p_msg->param.input.type, p_msg->param.input.status);
    if(p_msg->param.input.type == INPUT_TYPE_DIAL0) {
      playbackCtrl_playNext();
    }
    break;
  }
}

static RET playbackCtrl_init()
{
  RET ret;
  /*** register input ***/
  MSG_STRUCT *p_sendMsg;
  /* register to be notified when mode key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_OTHER0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /* register to be notified when capture key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_DIAL0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /*** init display ***/
  ret = display_init();
//  ret |= display_drawRect(0, 0, 100, 100, DISPLAY_COLOR_RED);
  if(ret != RET_OK) LOG_E("\n");

  /*** init file ***/
  ret = file_seekStart("/");
  if(ret != RET_OK) LOG_E("\n");

  /*** display the first image ***/
  ret = playbackCtrl_playNext();
  if(ret != RET_OK) LOG_E("\n");

  LOG("init %d\n", ret);

  return ret;
}

static RET playbackCtrl_exit()
{
  RET ret;
  /*** unregister input ***/
  MSG_STRUCT *p_sendMsg;
  /* register to be notified when mode key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_UNREGISTER;
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_OTHER0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /* register to be notified when capture key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_UNREGISTER;
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_DIAL0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /*** exit file ***/
  ret = file_seekStop();

  LOG("exit %d\n", ret);

  return ret;
}

static RET playbackCtrl_playNext()
{
  RET ret;
  char filename[16];
  ret = file_seekFileNext(filename);
  if(ret == RET_OK) {
    LOG("play %s\n", filename);
    if(playbackCtrl_isFileRGB565(filename) == RET_OK) playbackCtrl_playRGB565(filename);
    if(playbackCtrl_isFileJPEG(filename) == RET_OK) playbackCtrl_playJPEG(filename);
  } else {
    /* reached the end of files, or just error occured */
    LOG("dir end\n");
    ret =file_seekStop();
    ret |= file_seekStart("/");
    if(ret != RET_OK)LOG_E("\n");
  }

  return ret;
}

static RET playbackCtrl_isFileRGB565(char *filename)
{
  for(uint32_t i = 0; (i < 16) && (filename[i] != '\0'); i++) {
    if( (filename[i] == '.') && (filename[i+1] == 'R') && (filename[i+2] == 'G') && (filename[i+3] == 'B') )
      return RET_OK;
  }
  return RET_NO_DATA;
}

static RET playbackCtrl_isFileJPEG(char *filename)
{
  for(uint32_t i = 0; (i < 16) && (filename[i] != '\0'); i++) {
    if( (filename[i] == '.') && (filename[i+1] == 'J') && (filename[i+2] == 'P') )
      return RET_OK;
  }
  return RET_NO_DATA;
}

static RET playbackCtrl_playRGB565(char* filename)
{
  uint32_t num;
  RET ret;

  uint16_t* pLineBuff = pvPortMalloc(PLAY_SIZE_WIDTH*2);
  if(pLineBuff == 0) {
    LOG_E("\n");
    return RET_ERR;
  }

  ret = file_loadStart(filename);
  ret |= display_setArea(0, 0, PLAY_SIZE_WIDTH - 1, PLAY_SIZE_HEIGHT - 1);

  for(uint32_t i = 0; i < PLAY_SIZE_HEIGHT; i++){
    ret |= file_load(pLineBuff, PLAY_SIZE_WIDTH * 2, &num);
    display_drawBuffer(pLineBuff, num / 2);
    if( ret != RET_OK || num != PLAY_SIZE_WIDTH*2) break;
  }
  ret |= file_loadStop();

  vPortFree(pLineBuff);

  if(ret != RET_OK) LOG_E("%d\n", ret);

  return ret;
}

static RET playbackCtrl_playJPEG(char* filename)
{
  RET ret = RET_OK;
  FIL* pFil;
  ret |= display_setArea(0, 0, PLAY_SIZE_WIDTH - 1, PLAY_SIZE_HEIGHT - 1);

  uint8_t* pLineBuffRGB888 = pvPortMalloc(PLAY_SIZE_WIDTH*3);
  if(pLineBuffRGB888 == 0) {
    LOG_E("\n");
    return RET_ERR;
  }

  ret |= file_loadStart(filename);
  pFil = file_loadGetCurrentFil();
  if(pFil == 0 || ret != RET_OK) {
    LOG_E("%d\n", ret);
    file_loadStop();
    vPortFree(pLineBuffRGB888);
    return RET_ERR;
  }

  ret |= playbackCtrl_decodeJpeg(pFil, PLAY_SIZE_WIDTH, PLAY_SIZE_HEIGHT, pLineBuffRGB888);
  ret |= file_loadStop();
  vPortFree(pLineBuffRGB888);

  if(ret != RET_OK) LOG_E("%d\n", ret);

  return ret;
}

static RET playbackCtrl_decodeJpeg(FIL *file, uint32_t maxWidth, uint32_t maxHeight, uint8_t * pLineBuffRGB888)
{
  int ret = 0;;
  static struct jpeg_decompress_struct cinfo;
  static struct jpeg_error_mgr jerr;
  JSAMPROW buffer[2] = {0};

  buffer[0] = pLineBuffRGB888;

  cinfo.err = jpeg_std_error( &jerr );
  cinfo.err->output_message = playbackCtrl_libjpeg_output_message;  // over-write error output function

  jpeg_create_decompress(&cinfo);
  jpeg_stdio_src(&cinfo, file);
  ret = jpeg_read_header(&cinfo, TRUE);
  if(ret != JPEG_HEADER_OK) {
    LOG_E("%d\n", ret);
    jpeg_destroy_decompress(&cinfo);
    return RET_ERR;
  }

  ret = playbackCtrl_calcJpegOutputSize(&cinfo, maxWidth, maxHeight);
  if(ret != RET_OK) {
    LOG_E("unsupported size %d %d\n", cinfo.image_width, cinfo.image_height);
    jpeg_destroy_decompress(&cinfo);
    return RET_ERR;
  }

  cinfo.dct_method = JDCT_IFAST;
//  cinfo.dither_mode = JDITHER_ORDERED;
  cinfo.do_fancy_upsampling = FALSE;

  ret = jpeg_start_decompress(&cinfo);
  if(ret != 1) {
    LOG_E("%d\n", ret);
    jpeg_destroy_decompress(&cinfo);
    return RET_ERR;
  }

//  uint32_t start = HAL_GetTick();
  while( cinfo.output_scanline < cinfo.output_height ) {
    jpeg_read_scanlines(&cinfo, buffer, 1);
    playbackCtrl_drawRGB888(pLineBuffRGB888, cinfo.output_width);
  }
//  printf("%d\n", HAL_GetTick() - start);

  ret = jpeg_finish_decompress(&cinfo);
  if(ret != 1) {
    LOG_E("%d\n", ret);
  }
  jpeg_destroy_decompress(&cinfo);

  return RET_OK;
}

static void playbackCtrl_libjpeg_output_message (j_common_ptr cinfo)
{
  char buffer[JMSG_LENGTH_MAX];
  /* Create the message */
  (*cinfo->err->format_message) (cinfo, buffer);
  printf( "%s\n", buffer);
}

static void playbackCtrl_drawRGB888 (uint8_t* rgb888, uint32_t width)
{
  for(uint32_t x = 0; x < width; x++) {
    uint16_t rgb565;
    rgb565 = (((*rgb888)<<8)&0xF800) | (((*(rgb888+1))<<3)&0x07E0) | (((*(rgb888+2))>>3)&0x001F);
    display_putPixelRGB565(rgb565);
    rgb888 += 3;
  }
}

static RET playbackCtrl_calcJpegOutputSize(struct jpeg_decompress_struct* pCinfo, uint32_t maxWidth, uint32_t maxHeight)
{
  RET ret;
  if( (pCinfo->image_width == maxWidth) && (pCinfo->image_height == maxHeight) ) return RET_OK;

  uint32_t scaleX = 8, scaleY = 8;    // real scale = scale / 8
  if(pCinfo->image_width <= maxWidth) {
    scaleX = 8;
  } else if(pCinfo->image_width/2 <= maxWidth) {
    scaleX = 4;
  } else if(pCinfo->image_width/4 <= maxWidth) {
    scaleX = 2;
  } else if(pCinfo->image_width/8 <= maxWidth) {
    scaleX = 1;
  } else {
    return RET_ERR;
  }
  scaleY = scaleX;
  if (pCinfo->image_height / scaleY > maxHeight){
    if(pCinfo->image_height <= maxHeight) {
      scaleY = 8;
    } else if(pCinfo->image_height/2 <= maxHeight) {
      scaleY = 4;
    } else if(pCinfo->image_height/4 <= maxHeight) {
      scaleY = 2;
    } else if(pCinfo->image_height/8 <= maxHeight) {
      scaleY = 1;
    } else {
      return RET_ERR;
    }
  }
  scaleX = scaleY;
  pCinfo->scale_num = scaleX;
  pCinfo->scale_denom = 8;

  jpeg_calc_output_dimensions(pCinfo);

  display_drawRect(0, 0, maxWidth, maxHeight, 0x0000);

  ret = display_setArea( (maxWidth - pCinfo->output_width) / 2, (maxHeight - pCinfo->output_height) / 2,
                         (maxWidth + pCinfo->output_width) / 2 - 1, (maxHeight + pCinfo->output_height) / 2 - 1);
  return ret;
}



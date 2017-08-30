/*
 * playbackCtrl.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "ff.h"
#include "jpeglib.h"
#include "common.h"
#include "commonHigh.h"
#include "applicationSettings.h"
#include "../hal/display.h"
#include "../service/file.h"

/*** Internal Const Values, Macros ***/
#define LOG(str, ...) printf("[PB_CTRL:%d] " str, __LINE__, ##__VA_ARGS__);
#define LOG_E(str, ...) printf("[PB_CTRL_ERR:%d] " str, __LINE__, ##__VA_ARGS__);


// need to modify jdatasrc.c
#define INPUT_BUF_SIZE  512  /* choose an efficiently fread'able size */

typedef enum {
  INACTIVE,
  ACTIVE,
  MOVIE_PLAYING,
  MOVIE_PAUSE,
} STATUS;

/*** Internal Static Variables ***/
static STATUS s_status = INACTIVE;

static uint8_t *sp_movieLineBuffRGB888;   // line buffer for motion jpeg
static FIL     *sp_movieFil;              // file for motion jpeg

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
static RET playbackCtrl_isFileMotionJPEG(char *filename);
static RET playbackCtrl_decodeJpeg(FIL *p_file, uint32_t maxWidth, uint32_t maxHeight, uint8_t * p_lineBuffRGB888);
static void playbackCtrl_libjpeg_output_message (j_common_ptr cinfo);
static void playbackCtrl_drawRGB888 (uint8_t* rgb888, uint32_t width);
static RET playbackCtrl_calcJpegOutputSize(struct jpeg_decompress_struct* p_cinfo, uint32_t maxWidth, uint32_t maxHeight);
static RET playbackCtrl_playMotionJPEGStart(char* filename);
static RET playbackCtrl_playMotionJPEGStop();
static RET playbackCtrl_playMotionJPEGNext();

/*** External Function Defines ***/
void playbackCtrl_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(PLAYBACK_CTRL);
  uint32_t waitTimeForFPS = MOTION_JPEG_FPS_MSEC_EX;

  while(1) {
    osEvent event;
    if(s_status == MOVIE_PLAYING) {
      /* fps for movie play is controlled by wait timeout */
      event = osMessageGet(myQueueId, waitTimeForFPS);
    } else {
      event = osMessageGet(myQueueId, osWaitForever);
    }
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
//      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
      switch(s_status) {
      case INACTIVE:
        playbackCtrl_procInactive(p_recvMsg);
        break;
      case ACTIVE:
      case MOVIE_PLAYING:
      case MOVIE_PAUSE:
        playbackCtrl_procActive(p_recvMsg);
        break;
      }
      freeMemoryPoolMessage(p_recvMsg);
    } else if (event.status == osEventTimeout) {
      uint32_t startTime = HAL_GetTick();
      playbackCtrl_playMotionJPEGNext();
      uint32_t playTime = HAL_GetTick() - startTime;
      // adjust fps
      if(playTime > MOTION_JPEG_FPS_MSEC) {
        waitTimeForFPS = 1;
      } else {
        waitTimeForFPS = MOTION_JPEG_FPS_MSEC_EX - playTime;
      }
    }
  }
}

/*** Internal Function Defines ***/
static void playbackCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret)
{
  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // receiver must free
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
    /*** display the first image ***/
    ret |= playbackCtrl_playNext();
    if(ret != RET_OK) LOG_E("\n");
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
    } else if(p_msg->param.input.type == INPUT_TYPE_KEY_OTHER0) {
      if(s_status == MOVIE_PLAYING) {
        s_status = MOVIE_PAUSE;
      } else if(s_status == MOVIE_PAUSE) {
        s_status = MOVIE_PLAYING;
      }
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
  if(ret != RET_OK) LOG_E("%08X\n", ret);

  /*** init file ***/
  ret = file_seekStart("/");
  if(ret != RET_OK) LOG_E("%08X\n", ret);

  LOG("init %08X\n", ret);

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

  /*** exit movie play if playing ***/
  if( (s_status == MOVIE_PLAYING) || (s_status == MOVIE_PAUSE) ) {
    playbackCtrl_playMotionJPEGStop();
  }

  /*** exit file ***/
  ret = file_seekStop();

  LOG("exit %d\n", ret);

  return ret;
}

static RET playbackCtrl_playNext()
{
  RET ret;
  char filename[16];

  /* exit movie play if playing */
  if( (s_status == MOVIE_PLAYING) || (s_status == MOVIE_PAUSE) ) {
    playbackCtrl_playMotionJPEGStop();
  }

  ret = file_seekFileNext(filename);
  if(ret == RET_OK) {
    LOG("play %s\n", filename);
    if(playbackCtrl_isFileRGB565(filename) == RET_OK) playbackCtrl_playRGB565(filename);
    if(playbackCtrl_isFileJPEG(filename) == RET_OK) playbackCtrl_playJPEG(filename);
    if(playbackCtrl_isFileMotionJPEG(filename) == RET_OK) playbackCtrl_playMotionJPEGStart(filename);
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
  /* check if the extension is rgb */
  for(uint32_t i = 0; (i < 16) && (filename[i] != '\0'); i++) {
    if( (filename[i] == '.') && (filename[i+1] == 'R') && (filename[i+2] == 'G') && (filename[i+3] == 'B') )
      return RET_OK;
  }
  return RET_NO_DATA;
}

static RET playbackCtrl_isFileJPEG(char *filename)
{
  /* check if the extension is jpg */
  for(uint32_t i = 0; (i < 16) && (filename[i] != '\0'); i++) {
    if( (filename[i] == '.') && (filename[i+1] == 'J') && (filename[i+2] == 'P') )
      return RET_OK;
  }
  return RET_NO_DATA;
}

static RET playbackCtrl_isFileMotionJPEG(char *filename)
{
  /* check if the extension is avi */
  for(uint32_t i = 0; (i < 16) && (filename[i] != '\0'); i++) {
    if( (filename[i] == '.') && (filename[i+1] == 'A') && (filename[i+2] == 'V') )
      return RET_OK;
  }
  return RET_NO_DATA;
}
static RET playbackCtrl_playRGB565(char* filename)
{
  uint32_t num;
  RET ret;

  uint16_t* pLineBuff = pvPortMalloc(IMAGE_SIZE_WIDTH*2);
  if(pLineBuff == 0) {
    LOG_E("\n");
    return RET_ERR;
  }

  ret = file_loadStart(filename);
  ret |= display_setArea(0, 0, IMAGE_SIZE_WIDTH - 1, IMAGE_SIZE_HEIGHT - 1);

  for(uint32_t i = 0; i < IMAGE_SIZE_HEIGHT; i++){
    ret |= file_load(pLineBuff, IMAGE_SIZE_WIDTH * 2, &num);
    display_writeImage(pLineBuff, num / 2);
    if( ret != RET_OK || num != IMAGE_SIZE_WIDTH*2) break;
  }
  ret |= file_loadStop();

  vPortFree(pLineBuff);

  if(ret != RET_OK) LOG_E("%d\n", ret);

  return ret;
}

static RET playbackCtrl_playJPEG(char* filename)
{
  RET ret = RET_OK;
  FIL* p_fil;
  ret |= display_setArea(0, 0, IMAGE_SIZE_WIDTH - 1, IMAGE_SIZE_HEIGHT - 1);

  uint8_t* p_lineBuffRGB888 = pvPortMalloc(IMAGE_SIZE_WIDTH*3);
  if(p_lineBuffRGB888 == 0) {
    LOG_E("\n");
    return RET_ERR;
  }

  ret |= file_loadStart(filename);
  p_fil = file_loadGetCurrentFil();
  if(p_fil == 0 || ret != RET_OK) {
    LOG_E("%d\n", ret);
    file_loadStop();
    vPortFree(p_lineBuffRGB888);
    return RET_ERR;
  }

  display_drawRect(0, 0, IMAGE_SIZE_WIDTH, IMAGE_SIZE_HEIGHT, 0x0000);
  ret |= playbackCtrl_decodeJpeg(p_fil, IMAGE_SIZE_WIDTH, IMAGE_SIZE_HEIGHT, p_lineBuffRGB888);
  ret |= file_loadStop();
  vPortFree(p_lineBuffRGB888);

  if(ret != RET_OK) LOG_E("%d\n", ret);

  return ret;
}

static RET playbackCtrl_decodeJpeg(FIL *p_file, uint32_t maxWidth, uint32_t maxHeight, uint8_t * p_lineBuffRGB888)
{
  int ret = 0;;
  struct jpeg_decompress_struct* p_cinfo = pvPortMalloc(sizeof(struct jpeg_decompress_struct));
  struct jpeg_error_mgr* p_jerr = pvPortMalloc(sizeof(struct jpeg_error_mgr));
  JSAMPROW buffer[2] = {0};

  buffer[0] = p_lineBuffRGB888;

  p_cinfo->err = jpeg_std_error(p_jerr);
  p_cinfo->err->output_message = playbackCtrl_libjpeg_output_message;  // over-write error output function

  jpeg_create_decompress(p_cinfo);
  jpeg_stdio_src(p_cinfo, p_file);
  ret = jpeg_read_header(p_cinfo, TRUE);
  if(ret != JPEG_HEADER_OK) {
    LOG_E("%d\n", ret);
    jpeg_destroy_decompress(p_cinfo);
    return RET_ERR;
  }

  ret = playbackCtrl_calcJpegOutputSize(p_cinfo, maxWidth, maxHeight);
  if(ret != RET_OK) {
    LOG_E("unsupported size %d %d\n", p_cinfo->image_width, p_cinfo->image_height);
    jpeg_destroy_decompress(p_cinfo);
    return RET_ERR;
  }

  p_cinfo->dct_method = JDCT_IFAST;
//  p_cinfo->dither_mode = JDITHER_ORDERED;
  p_cinfo->do_fancy_upsampling = FALSE;

  ret = jpeg_start_decompress(p_cinfo);
  if(ret != 1) {
    LOG_E("%d\n", ret);
    jpeg_destroy_decompress(p_cinfo);
    return RET_ERR;
  }

//  uint32_t start = HAL_GetTick();
  while( p_cinfo->output_scanline < p_cinfo->output_height ) {
    jpeg_read_scanlines(p_cinfo, buffer, 1);
    playbackCtrl_drawRGB888(p_lineBuffRGB888, p_cinfo->output_width);
  }
//  printf("%d\n", HAL_GetTick() - start);

  ret = jpeg_finish_decompress(p_cinfo);
  if(ret != 1) {
    LOG_E("%d\n", ret);
  }
  jpeg_destroy_decompress(p_cinfo);

  vPortFree(p_cinfo);
  vPortFree(p_jerr);
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

static RET playbackCtrl_calcJpegOutputSize(struct jpeg_decompress_struct* p_cinfo, uint32_t maxWidth, uint32_t maxHeight)
{
  RET ret;
  if( (p_cinfo->image_width == maxWidth) && (p_cinfo->image_height == maxHeight) ) return RET_OK;

  uint32_t scaleX = 8, scaleY = 8;    // real scale = scale / 8
  if(p_cinfo->image_width <= maxWidth) {
    scaleX = 8;
  } else if(p_cinfo->image_width/2 <= maxWidth) {
    scaleX = 4;
  } else if(p_cinfo->image_width/4 <= maxWidth) {
    scaleX = 2;
  } else if(p_cinfo->image_width/8 <= maxWidth) {
    scaleX = 1;
  } else {
    return RET_ERR;
  }
  scaleY = scaleX;
  if (p_cinfo->image_height / scaleY > maxHeight){
    if(p_cinfo->image_height <= maxHeight) {
      scaleY = 8;
    } else if(p_cinfo->image_height/2 <= maxHeight) {
      scaleY = 4;
    } else if(p_cinfo->image_height/4 <= maxHeight) {
      scaleY = 2;
    } else if(p_cinfo->image_height/8 <= maxHeight) {
      scaleY = 1;
    } else {
      return RET_ERR;
    }
  }
  scaleX = scaleY;
  p_cinfo->scale_num = scaleX;
  p_cinfo->scale_denom = 8;

  jpeg_calc_output_dimensions(p_cinfo);

  ret = display_setArea( (maxWidth - p_cinfo->output_width) / 2, (maxHeight - p_cinfo->output_height) / 2,
                         (maxWidth + p_cinfo->output_width) / 2 - 1, (maxHeight + p_cinfo->output_height) / 2 - 1);
  return ret;
}

static RET playbackCtrl_playMotionJPEGStart(char* filename)
{
  RET ret;
  ret = display_drawRect(0, 0, IMAGE_SIZE_WIDTH, IMAGE_SIZE_HEIGHT, 0x0000);

  if( (sp_movieLineBuffRGB888 != 0) || (sp_movieFil != 0) ){
    LOG_E("for got stopping movie play\n");
    return RET_ERR_STATUS;
  }

  sp_movieLineBuffRGB888 = pvPortMalloc(IMAGE_SIZE_WIDTH*3);
  if(sp_movieLineBuffRGB888 == 0) {
    LOG_E("\n");
    return RET_ERR;
  }

  ret |= file_loadStart(filename);
  sp_movieFil = file_loadGetCurrentFil();
  if(sp_movieFil == 0 || ret != RET_OK) {
    LOG_E("%d\n", ret);
    file_loadStop();
    vPortFree(sp_movieLineBuffRGB888);
    return RET_ERR;
  }

  s_status = MOVIE_PLAYING;

  return ret;
}

static RET playbackCtrl_playMotionJPEGStop()
{
  RET ret = RET_OK;
  ret |= file_loadStop();
  vPortFree(sp_movieLineBuffRGB888);


  s_status = ACTIVE;
  sp_movieLineBuffRGB888 = 0;
  sp_movieFil = 0;

  if(ret != RET_OK) LOG_E("%d\n", ret);
  return ret;
}

static RET playbackCtrl_playMotionJPEGNext()
{
  RET ret;
  ret = playbackCtrl_decodeJpeg(sp_movieFil, IMAGE_SIZE_WIDTH, IMAGE_SIZE_HEIGHT, sp_movieLineBuffRGB888);
  if(ret != RET_OK) {
    LOG_E("%d\n", ret);
    playbackCtrl_playMotionJPEGStop();
    return ret;
  }

  /* move back to the end of last EOI(0xFF 0xD9) because libjpeg might has read too many*/
  /* the border between JPEG(n-1) and JPEG(n) is 0xFF 0xD9 0xFF 0xD8 0xFF 0xE0 */
  uint8_t buff[3] = {0};
  uint32_t num;
  if(f_tell(sp_movieFil) > INPUT_BUF_SIZE){
    /* 1. move back by 512 byte anyway */
    f_lseek(sp_movieFil, f_tell(sp_movieFil) - INPUT_BUF_SIZE);
    /* 2. then, search for EOI */
    while(1) {
      ret = file_load(buff, 2, &num);
      if( (ret == RET_OK) && (num == 2) ) {
        if( (buff[0] == 0xFF) && (buff[1] == 0xD9) ){
          break;
        } else if( (buff[2] == 0xFF) && (buff[0] == 0xD9) ){
          /* for odd address */
          f_lseek(sp_movieFil, f_tell(sp_movieFil) - 1);
          break;
        } else {
          buff[2] = buff[1];
          continue;
        }
      } else {
        /* end of file */
        playbackCtrl_playMotionJPEGStop();
        break;
      }
    }
  } else {
    /* something is wrong */
    LOG_E("%d\n", f_tell(sp_movieFil));
    playbackCtrl_playMotionJPEGStop();
  }

  if( f_tell(sp_movieFil) == f_size(sp_movieFil) ){
    /* end of file */
    playbackCtrl_playMotionJPEGStop();
  }

  return RET_OK;
}

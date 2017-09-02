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
#include "commonMsg.h"
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

// for motion jpeg
static FIL     *sp_movieFil;
static uint32_t s_lastFrameStartTimeMSec; // for fps control
static uint32_t s_currentTargetFPS;

/*** Internal Function Declarations ***/
static void playbackCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret);
static void playbackCtrl_processMsg(MSG_STRUCT *p_msg);
static void playbackCtrl_processFrame();
static RET playbackCtrl_init();
static RET playbackCtrl_exit();
static RET playbackCtrl_playNext(); // call this when dial rotated

static RET playbackCtrl_isFileJPEG(char *filename);
static RET playbackCtrl_isFileRGB565(char *filename);
static RET playbackCtrl_isFileMotionJPEG(char *filename);

static RET playbackCtrl_playRGB565(char* filename);
static RET playbackCtrl_playJPEG(char* filename);

static RET playbackCtrl_playMotionJPEGStart(char* filename);
static RET playbackCtrl_playMotionJPEGStop();
static RET playbackCtrl_playMotionJPEGNext();

static RET playbackCtrl_decodeJpeg(FIL *p_file, uint32_t maxWidth, uint32_t maxHeight);
static void playbackCtrl_libjpeg_output_message (j_common_ptr cinfo);
static void playbackCtrl_drawRGB888 (uint8_t* rgb888, uint32_t width);
static RET playbackCtrl_calcJpegOutputSize(struct jpeg_decompress_struct* p_cinfo, uint32_t maxWidth, uint32_t maxHeight);


/*** External Function Defines ***/
void playbackCtrl_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(PLAYBACK_CTRL);

  while(1) {
    osEvent event;
    event = osMessageGet(myQueueId, 10);
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
//      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
      playbackCtrl_processMsg(p_recvMsg);
      freeMemoryPoolMessage(p_recvMsg);
    } else if (event.status == osEventTimeout) {
      playbackCtrl_processFrame();
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

static void playbackCtrl_processMsg(MSG_STRUCT *p_msg)
{
  RET ret;

  switch(s_status) {

  case INACTIVE:
    switch(p_msg->command){
    case CMD_START:
      ret = playbackCtrl_init();
      if(ret == RET_OK) {
        s_status = ACTIVE;
        /*** display the first image ***/
        ret |= playbackCtrl_playNext();
      }
      if(ret != RET_OK) LOG_E("%08X\n", ret);
      playbackCtrl_sendComp(p_msg, ret);
      break;
    case CMD_STOP:
      LOG_E("status error\n");
      playbackCtrl_sendComp(p_msg, RET_ERR_STATUS);
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
  case MOVIE_PLAYING:
  case MOVIE_PAUSE:
    switch(p_msg->command){
    case CMD_START:
      LOG_E("status error\n");
      playbackCtrl_sendComp(p_msg, RET_ERR_STATUS);
      break;
    case CMD_STOP:
      ret = playbackCtrl_exit();
      if(ret == RET_OK) {
        s_status = INACTIVE;
      }
      playbackCtrl_sendComp(p_msg, ret);
      break;
    case CMD_NOTIFY_INPUT:
      LOG("input: %d %d\n", p_msg->param.input.type, p_msg->param.input.param);
      if(p_msg->param.input.type == INPUT_TYPE_DIAL0) {
        playbackCtrl_playNext();
      } else if(p_msg->param.input.type == INPUT_TYPE_KEY_OTHER0) {
        if(s_status == MOVIE_PLAYING) {
          s_status = MOVIE_PAUSE;
          display_osdMark(DISPLAY_OSD_TYPE_PAUSE);
        } else if(s_status == MOVIE_PAUSE) {
          s_status = MOVIE_PLAYING;
        }
      }
      break;
    }
    break;

  default:
    LOG_E("status error\n");
    break;
  }
}

static void playbackCtrl_processFrame()
{
  if(s_status == MOVIE_PLAYING) {
    if(HAL_GetTick() - s_lastFrameStartTimeMSec > s_currentTargetFPS) { // control fps
      s_lastFrameStartTimeMSec = HAL_GetTick();
      playbackCtrl_playMotionJPEGNext();
    } else {
      // skip for fps control
    }
  } else {
    /* do nothing */
  }

}

static RET playbackCtrl_init()
{
  RET ret = RET_OK;

  /*** register input ***/
  MSG_STRUCT *p_sendMsg;
  /* register to be notified when mode key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_KEY_OTHER0;
  p_sendMsg->param.input.param = 1; // notify every 1 ticks;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /* register to be notified when capture key pressed */
  p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->command = CMD_REGISTER;
  p_sendMsg->sender  = PLAYBACK_CTRL;
  p_sendMsg->param.input.type = INPUT_TYPE_DIAL0;
  osMessagePut(getQueueId(INPUT), (uint32_t)p_sendMsg, osWaitForever);

  /*** init display ***/
  ret |= display_init();

  /*** init file ***/
  ret |= file_init();
  ret |= file_seekStart("/");

  if(ret != RET_OK) LOG_E("%08X\n", ret);

  return ret;
}

static RET playbackCtrl_exit()
{
  RET ret = RET_OK;

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
    ret |= playbackCtrl_playMotionJPEGStop();
  }

  /*** exit file ***/
  ret |= file_seekStop();
  ret |= file_deinit();

  if(ret != RET_OK) LOG_E("%08X\n", ret);

  return ret;
}

static RET playbackCtrl_playNext()
{
  RET ret = RET_OK;
  char filename[16];

  /* exit movie play if playing */
  if( (s_status == MOVIE_PLAYING) || (s_status == MOVIE_PAUSE) ) {
    playbackCtrl_playMotionJPEGStop();
  }

  /* search for the next iamge file to be displayed */
  ret = file_seekFileNext(filename);
  if(ret == RET_OK) {
    LOG("play %s\n", filename);
    display_drawRect(0, 0, IMAGE_SIZE_WIDTH, IMAGE_SIZE_HEIGHT, DISPLAY_COLOR_BLACK);  // background
    /* check file extension, then play the image in an appropriate manner */
    if(playbackCtrl_isFileRGB565(filename) == RET_OK)     ret |= playbackCtrl_playRGB565(filename);
    if(playbackCtrl_isFileJPEG(filename) == RET_OK)       ret |= playbackCtrl_playJPEG(filename);
    if(playbackCtrl_isFileMotionJPEG(filename) == RET_OK) ret |= playbackCtrl_playMotionJPEGStart(filename);
  } else {
    /* reached the end of files, or just error occurred */
    LOG("dir end\n");
    /* restart search directory */
    ret = file_seekStop();
    ret |= file_seekStart("/");
  }

  if(ret != RET_OK)LOG_E("%08X\n", ret);

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
  uint8_t isAvi = 0;
  /* check if the extension is avi */
  for(uint32_t i = 0; (i < 16) && (filename[i] != '\0'); i++) {
    if( (filename[i] == '.') && (filename[i+1] == 'A') && (filename[i+2] == 'V') ) {
      isAvi = 1;
    }
  }

  if(!isAvi) return RET_NO_DATA;

  if( (filename[0] == FILENAME_MOVIE[0]) && (filename[1] == FILENAME_MOVIE[1]) && (filename[2] == FILENAME_MOVIE[2]) ) {
    // motion jpeg file recorded by this device
    s_currentTargetFPS = MOTION_JPEG_FPS_MSEC;
  } else {
    // motion jpeg file created by another device such as PC
    s_currentTargetFPS = MOTION_JPEG_FPS_MSEC_EX;
  }

  return RET_OK;
}

static RET playbackCtrl_playRGB565(char* filename)
{
  RET ret = RET_OK;
  uint32_t num;

  uint16_t* p_lineBuffRGB565 = pvPortMalloc(IMAGE_SIZE_WIDTH * 2);
  if(p_lineBuffRGB565 == 0) {
    LOG_E("\n");
    return RET_ERR_MEMORY;
  }

  ret |= display_setArea(0, 0, IMAGE_SIZE_WIDTH - 1, IMAGE_SIZE_HEIGHT - 1);
  ret |= file_loadStart(filename);

  for(uint32_t i = 0; i < IMAGE_SIZE_HEIGHT; i++){
    ret |= file_load(p_lineBuffRGB565, IMAGE_SIZE_WIDTH * 2, &num);
    display_writeImage(p_lineBuffRGB565, num / 2);
    if( ret != RET_OK || num != IMAGE_SIZE_WIDTH*2) {
      LOG_E("something is wrong\n");
      break;
    }
  }
  ret |= file_loadStop();

  vPortFree(p_lineBuffRGB565);

  if(ret != RET_OK)LOG_E("%08X\n", ret);

  return ret;
}

static RET playbackCtrl_playJPEG(char* filename)
{
  RET ret = RET_OK;
  FIL* p_fil;

  ret |= display_setArea(0, 0, IMAGE_SIZE_WIDTH - 1, IMAGE_SIZE_HEIGHT - 1);

  ret  |= file_loadStart(filename);
  p_fil = file_loadGetCurrentFil();
  if(p_fil == 0 || ret != RET_OK) {
    LOG_E("%d\n", ret);
    file_loadStop();
    return RET_ERR_FILE | ret;
  }

  ret |= playbackCtrl_decodeJpeg(p_fil, IMAGE_SIZE_WIDTH, IMAGE_SIZE_HEIGHT);
  ret |= file_loadStop();

  if(ret != RET_OK)LOG_E("%08X\n", ret);

  return ret;
}

static RET playbackCtrl_playMotionJPEGStart(char* filename)
{
  RET ret = RET_OK;

  if( sp_movieFil != 0 ){
    LOG_E("forgot stopping movie play\n");
    return RET_ERR_STATUS;
  }

  ret |= file_loadStart(filename);    // keep this open during movie play
  sp_movieFil = file_loadGetCurrentFil();
  if(sp_movieFil == 0 || ret != RET_OK) {
    LOG_E("%d\n", ret);
    file_loadStop();
    return RET_ERR;
  }

  s_status = MOVIE_PLAYING;
  s_lastFrameStartTimeMSec = HAL_GetTick();

  /* the first image is displayed at the next frame (playbackCtrl_playMotionJPEGNext) */

  return ret;
}

static RET playbackCtrl_playMotionJPEGStop()
{
  RET ret = RET_OK;
  ret |= file_loadStop();

  display_osdMark(DISPLAY_OSD_TYPE_STOP);

  s_status = ACTIVE;
  sp_movieFil = 0;

  if(ret != RET_OK) LOG_E("%d\n", ret);
  return ret;
}

static RET playbackCtrl_playMotionJPEGNext()
{
  RET ret;
  ret = playbackCtrl_decodeJpeg(sp_movieFil, IMAGE_SIZE_WIDTH, IMAGE_SIZE_HEIGHT);
  if(ret != RET_OK) {
    LOG_E("%d\n", ret);
    playbackCtrl_playMotionJPEGStop();
    return ret;
  }

  /* prepare FIL object for the next frame JPEG (in the same file) */
  /* move FIL pointer back to the end of last EOI(0xFF 0xD9) because libjpeg might has read too many*/
  /* the border between JPEG(n-1) and JPEG(n) is 0xFF 0xD9 0xFF 0xD8 0xFF 0xE0 */
  uint8_t buff[3] = {0};
  uint32_t num;
  if(f_tell(sp_movieFil) > INPUT_BUF_SIZE){
    /* 1. move back by 512 byte */
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
    /* something is wrong (or too small file?) */
    LOG_E("%d\n", f_tell(sp_movieFil));
    playbackCtrl_playMotionJPEGStop();
  }

  if( f_tell(sp_movieFil) == f_size(sp_movieFil) ){
    /* end of file */
    playbackCtrl_playMotionJPEGStop();
  }

  return RET_OK;
}


static RET playbackCtrl_decodeJpeg(FIL *p_file, uint32_t maxWidth, uint32_t maxHeight)
{
  int ret = 0;

  uint32_t start = HAL_GetTick();

  /*** alloc memory ***/
  struct jpeg_decompress_struct* p_cinfo = pvPortMalloc(sizeof(struct jpeg_decompress_struct));
  struct jpeg_error_mgr* p_jerr          = pvPortMalloc(sizeof(struct jpeg_error_mgr));
  uint8_t* p_lineBuffRGB888              = pvPortMalloc(IMAGE_SIZE_WIDTH*3);
  JSAMPROW buffer[2] = {0};

  if( (p_cinfo == 0) || (p_jerr == 0) || (p_lineBuffRGB888 == 0) ){
    LOG_E("not enough memory\n");
    vPortFree(p_cinfo);
    vPortFree(p_jerr);
    vPortFree(p_lineBuffRGB888);
    return RET_ERR_MEMORY;
  }

  /*** prepare libjpeg ***/
  buffer[0] = p_lineBuffRGB888;
  p_cinfo->err = jpeg_std_error(p_jerr);
  p_cinfo->err->output_message = playbackCtrl_libjpeg_output_message;  // over-write error output function
  jpeg_create_decompress(p_cinfo);
  jpeg_stdio_src(p_cinfo, p_file);

  /* get jpeg info to resize appropriate size */
  ret = jpeg_read_header(p_cinfo, TRUE);
  if(ret != JPEG_HEADER_OK) {
    LOG_E("%d\n", ret);
    jpeg_destroy_decompress(p_cinfo);
    vPortFree(p_cinfo);
    vPortFree(p_jerr);
    vPortFree(p_lineBuffRGB888);
    return RET_ERR;
  }

  /* calculate output size */
  ret = playbackCtrl_calcJpegOutputSize(p_cinfo, maxWidth, maxHeight);
  if(ret != RET_OK) {
    LOG_E("unsupported size %d %d\n", p_cinfo->image_width, p_cinfo->image_height);
    jpeg_destroy_decompress(p_cinfo);
    vPortFree(p_cinfo);
    vPortFree(p_jerr);
    vPortFree(p_lineBuffRGB888);
    return RET_ERR;
  }

  /* jpeg decode setting */
  p_cinfo->dct_method = JDCT_IFAST;
//  p_cinfo->dither_mode = JDITHER_ORDERED;
  p_cinfo->do_fancy_upsampling = FALSE;
  ret = jpeg_start_decompress(p_cinfo);
  if(ret != 1) {
    LOG_E("%d\n", ret);
    jpeg_destroy_decompress(p_cinfo);
    vPortFree(p_cinfo);
    vPortFree(p_jerr);
    vPortFree(p_lineBuffRGB888);
    return RET_ERR;
  }

  /*** decode jpeg and display it line by line ***/
  while( p_cinfo->output_scanline < p_cinfo->output_height ) {
    if (jpeg_read_scanlines(p_cinfo, buffer, 1) != 1) {
      LOG_E("Decode Stop at line %d\n", p_cinfo->output_scanline);
      break;
    }
    playbackCtrl_drawRGB888(p_lineBuffRGB888, p_cinfo->output_width);
  }

  ret = jpeg_finish_decompress(p_cinfo);
  if(ret != 1) {
    LOG_E("%d\n", ret);
  }
  jpeg_destroy_decompress(p_cinfo);

  vPortFree(p_cinfo);
  vPortFree(p_jerr);
  vPortFree(p_lineBuffRGB888);


//  printf("decode time = %d\n", HAL_GetTick() - start);

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
  if ((p_cinfo->image_height * scaleY) / 8 > maxHeight){
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


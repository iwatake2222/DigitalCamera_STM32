/*
 * captureCtrl.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */
#include <stdio.h>
#include "cmsis_os.h"
#include "common.h"
#include "commonHigh.h"
#include "../hal/camera.h"
#include "ff.h"
#include "jpeglib.h"

// todo delete
#include "stm32f4xx_hal.h"

/*** Internal Const Values, Macros ***/
#define LOG(str, ...) printf("[CAP_CTRL:%d] " str, __LINE__, ##__VA_ARGS__);
#define LOG_E(str, ...) printf("[CAP_CTRL_ERR:%d] " str, __LINE__, ##__VA_ARGS__);

#define CAPTURE_WIDTH  320
#define CAPTURE_HEIGHT 240

#define MSG_PARAM_LINE_READY   1
#define MSG_PARAM_FRAME_READY  2

/*** Internal Static Variables ***/
static uint32_t s_currentLine;
static uint8_t *sp_lineBuffEven;
static uint8_t *sp_lineBuffOdd;
static uint8_t *sp_lineBuffRGB888;

static uint8_t s_isValidFrame = 1;

static FATFS *sp_fatFs;
static FIL *sp_fil;
static struct jpeg_compress_struct* sp_cinfo;
static struct jpeg_error_mgr* sp_jerr;
static JSAMPROW s_jsampros[2] = {0};

/*** Internal Function Declarations ***/
static void captureCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret);
static void captureCtrl_sendCompCapDone(RET ret);
static void captureCtrl_sendOneLineReady(RET ret);
static RET captureCtrl_treatMsg(MSG_STRUCT *p_msg);
static RET captureCtrl_captureInit();
static void captureCtrl_encLine(uint32_t line);
static void captureCtrl_encFinalize();
static void captureCtrl_cbHsync(uint32_t line);
static void captureCtrl_cbVsync(uint32_t frame);

/*** External Function Defines ***/
void captureCtrl_task(void const * argument)
{
  LOG("task start\n");
  osMessageQId myQueueId = getQueueId(CAPTURE_CTRL);

  while(1) {
    osEvent event = osMessageGet(myQueueId, osWaitForever);
    if (event.status == osEventMessage) {
      MSG_STRUCT* p_recvMsg = event.value.p;
//      LOG("msg received: %08X %08X %08X\n", p_recvMsg->command, p_recvMsg->sender, p_recvMsg->param.val);
      captureCtrl_treatMsg(p_recvMsg);
      freeMemoryPoolMessage(p_recvMsg);
    }
  }

}

/*** Internal Function Defines ***/
static void captureCtrl_sendComp(MSG_STRUCT *p_recvMmsg, RET ret)
{
  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->sender  = CAPTURE_CTRL;
  p_sendMsg->command = COMMAND_COMP(p_recvMmsg->command);
  p_sendMsg->param.val = ret;
  osMessagePut(getQueueId(p_recvMmsg->sender), (uint32_t)p_sendMsg, osWaitForever);
}

static void captureCtrl_sendCompCapDone(RET ret)
{
  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->sender  = CAPTURE_CTRL;
  p_sendMsg->command = COMMAND_COMP(CMD_CAPTURE);
  p_sendMsg->param.val = ret;
  osMessagePut(getQueueId(MODE_MGR), (uint32_t)p_sendMsg, osWaitForever);
}

static void captureCtrl_sendOneLineReady(uint32_t line)
{
  // send message to myself
  MSG_STRUCT *p_sendMsg = allocMemoryPoolMessage(); // must free by receiver
  p_sendMsg->sender  = CAPTURE_CTRL;
  p_sendMsg->command = CMD_CAPTURE;
  p_sendMsg->param.val = line;
  osMessagePut(getQueueId(CAPTURE_CTRL), (uint32_t)p_sendMsg, osWaitForever);
}

static RET captureCtrl_treatMsg(MSG_STRUCT *p_msg)
{
  RET ret = RET_OK;
  if(p_msg->command == CMD_CAPTURE) {
    if(p_msg->sender == MODE_MGR) {
      ret = captureCtrl_captureInit();
    } else if(p_msg->sender == CAPTURE_CTRL) {
      /* encode one line when I receive message from myself(hsync interrupt) */
      captureCtrl_encLine(p_msg->param.val);
    }
  } else {
    ret = RET_ERR;
    captureCtrl_sendComp(p_msg, ret);
  }

  return ret;
}

static RET captureCtrl_captureInit()
{
  LOG("CAPTURE START\n");

  sp_lineBuffEven = pvPortMalloc(CAPTURE_WIDTH * 2);
  sp_lineBuffOdd  = pvPortMalloc(CAPTURE_WIDTH * 2);
  sp_lineBuffRGB888 = pvPortMalloc(CAPTURE_WIDTH * 3);

  if(sp_lineBuffEven == 0 || sp_lineBuffOdd == 0 || sp_lineBuffRGB888 == 0) {
    LOG_E("\n");
    return RET_ERR;
  }

  s_currentLine = 0;

  display_init();
  display_setArea(0, 0, CAPTURE_WIDTH - 1, CAPTURE_HEIGHT - 1);
  display_drawRect(0, 0, CAPTURE_WIDTH, CAPTURE_HEIGHT, 0xFFFF);


  /*** init camera ***/
  camera_init();
  camera_config(CAMERA_MODE_QVGA_RGB565);
  camera_registerCallback(captureCtrl_cbHsync, captureCtrl_cbVsync);
  ov7670_capLine2MemStart(sp_lineBuffEven);

  /*** init encoder ***/
//  sp_fatFs = pvPortMalloc(sizeof(FATFS));
//  sp_fil = pvPortMalloc(sizeof(FIL));
//  sp_cinfo = pvPortMalloc(sizeof(struct jpeg_compress_struct));
//  sp_jerr = pvPortMalloc(sizeof(struct jpeg_error_mgr));
//  s_jsampros[0] = sp_lineBuffRGB888;
//
//  sp_cinfo->err = jpeg_std_error(sp_jerr);
//  jpeg_create_compress(sp_cinfo);
//
//  f_mount(sp_fatFs, "", 0);
//  f_open(sp_fil, "qwe.jpg", FA_WRITE | FA_CREATE_ALWAYS);
//  jpeg_stdio_dest(sp_cinfo, sp_fil);
//
//  sp_cinfo->image_width = 320;
//  sp_cinfo->image_height = 240;
//  sp_cinfo->input_components = 3;
//  sp_cinfo->in_color_space = JCS_RGB;
//  jpeg_set_defaults(sp_cinfo);
//  jpeg_set_quality(sp_cinfo, 10, TRUE);
//  jpeg_start_compress(sp_cinfo, TRUE);
  return RET_OK;
}

static void captureCtrl_encLine(uint32_t line)
{
  uint16_t *p_buff;
  uint8_t  *p_buffRGB888 = sp_lineBuffRGB888;

  if(line%2 == 0) {
    p_buff = sp_lineBuffEven;
  } else {
    p_buff = sp_lineBuffOdd;
  }

  if(s_isValidFrame!=0) return;

//  HAL_Delay(4);

  /* display one line and convert rgb565 to rgb888 */
  // do not use DMA at this moment because DMA2 is occupied by DCMI (only DMA2 supports mem2mem)...
  uint32_t start = HAL_GetTick();
  for(uint32_t x = 0; x < 320; x++) {
    *((volatile uint16_t*)lcdIli9341_getDrawAddress()) = *p_buff;
    *(p_buffRGB888 + 0) = (((*p_buff) >> 11) & 0x1F) << 3;
    *(p_buffRGB888 + 1) = (((*p_buff) >>  5) & 0x3F) << 2;
    *(p_buffRGB888 + 2) = (((*p_buff) >>  0) & 0x1F) << 3;
    p_buffRGB888 += 3;
    p_buff++;
  }
  HAL_Delay(1);
//  if(line%10==2)HAL_Delay(2);

  // encode one line
//  jpeg_write_scanlines(sp_cinfo, s_jsampros, 1);

//  printf("%d\n", HAL_GetTick() - start);

  GPIO_InitTypeDef GPIO_InitStruct;
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF0_MCO;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
//  ov7670_writeExt(0x09, 0x01);
//
  if(line == CAPTURE_HEIGHT - 1) {
    captureCtrl_encFinalize();

  }
  return;
}

static void captureCtrl_encFinalize()
{
  /*** stop camera ***/
  s_isValidFrame = 4;

  camera_stopCap();
//  jpeg_finish_compress(sp_cinfo);
//  jpeg_destroy_compress(sp_cinfo);
//  f_close(sp_fil);
//
//  vPortFree(sp_cinfo);
//  vPortFree(sp_jerr);
//  vPortFree(sp_fil);
//  vPortFree(sp_fatFs);
//
//  f_mount(0, "", 0);

  vPortFree(sp_lineBuffEven);
  vPortFree(sp_lineBuffOdd);
  vPortFree(sp_lineBuffRGB888);
  LOG("CAPTURE DONE\n");
//  captureCtrl_sendCompCapDone(RET_OK);
}

static void captureCtrl_cbHsync(uint32_t line)
{
  /*** One line(n) is transferred ***/
//  LOG("%d %d\n", HAL_GetTick(), line);
  if (s_isValidFrame == 0) {
    if(line < CAPTURE_HEIGHT) {     // line > 1?
      // todo if encoding
      GPIO_InitTypeDef GPIO_InitStruct;
      GPIO_InitStruct.Pin = GPIO_PIN_8;
      GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
      GPIO_InitStruct.Pull = GPIO_NOPULL;
      GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
      HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
      HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
//      ov7670_writeExt(0x09, 0x10);

      /* start transfer next line (n+1) */
      if(line%2) {
        ov7670_capLine2MemNext(sp_lineBuffOdd);
      } else {
        ov7670_capLine2MemNext(sp_lineBuffEven);
      }
      /* invoke encode transferred line (n) */
      captureCtrl_sendOneLineReady(line);

    }
  }
}

static void captureCtrl_cbVsync(uint32_t frame)
{
  s_isValidFrame--;
  if(s_isValidFrame != 0) {
    ov7670_capLine2MemStart(sp_lineBuffEven);
  }
  printf("frame\n");
}

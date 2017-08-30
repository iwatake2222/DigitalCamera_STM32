#include <stdio.h>
#include "main.h"
#include "stm32f4xx_hal.h"

#include "common.h"
#include "uartTerminal.h"

/*** Internal Const Values, Macros ***/
#define BUFFER_SIZE 16
#define bufferRxWp ( (BUFFER_SIZE - sp_huart->hdmarx->Instance->NDTR) & (BUFFER_SIZE - 1) )

/*** Static Variables ***/
static UART_HandleTypeDef *sp_huart;
static volatile uint8_t s_bufferRx[BUFFER_SIZE];
static volatile uint8_t s_bufferRxRp = 0;

/*** Internal Function Declarations ***/

/*** External Function Defines ***/
RET uartTerminal_init(UART_HandleTypeDef *huart)
{
  sp_huart = huart;
  HAL_UART_Receive_DMA(sp_huart, s_bufferRx, BUFFER_SIZE);
  s_bufferRxRp = 0;

//  /* echo test */
//  while(1){
//    uartTerminal_send(uartTerminal_recv());
//  }
  return RET_OK;
}

RET uartTerminal_send(uint8_t data)
{
  HAL_StatusTypeDef ret;
  ret = HAL_UART_Transmit(sp_huart, &data, 1, 100);
  if (ret == HAL_OK ) {
    return RET_OK;
  } else {
    return RET_ERR;
  }
}

uint8_t uartTerminal_recv()
{
  uint8_t data = 0;
  while (bufferRxWp == s_bufferRxRp);
  data = s_bufferRx[s_bufferRxRp++];
  s_bufferRxRp &= (BUFFER_SIZE - 1);
  return data;
}

RET uartTerminal_recvTry(uint8_t *data)
{
  if (bufferRxWp == s_bufferRxRp)
    return RET_NO_DATA;
  *data = s_bufferRx[s_bufferRxRp++];
  s_bufferRxRp &= (BUFFER_SIZE - 1);
  return RET_OK;
}

/*** Internal Function Defines ***/

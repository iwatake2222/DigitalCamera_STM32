/*
 * retarget.c
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */

#include "stm32f4xx_hal.h"
#include <stdio.h>
#include "common.h"
#include "./driver/uartTerminal/uartTerminal.h"

void retarget_init()
{
  extern UART_HandleTypeDef huart2;
  uartTerminal_init(&huart2);
  setbuf(stdin, NULL);
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);
}


#ifdef __GNUC__
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif /* __GNUC__ */

PUTCHAR_PROTOTYPE
{
  uartTerminal_send(ch);
  return 1;
}

#ifdef __GNUC__
#define GETCHAR_PROTOTYPE int __io_getchar(void)
#else
#define GETCHAR_PROTOTYPE int fgetc(FILE *f)
#endif /* __GNUC__ */

GETCHAR_PROTOTYPE
{
  return uartTerminal_recv();
}

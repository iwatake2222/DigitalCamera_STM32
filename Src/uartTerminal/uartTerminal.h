/*
 * uartTerminal.h
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */

#ifndef UARTTERMINAL_UARTTERMINAL_H_
#define UARTTERMINAL_UARTTERMINAL_H_

RET uartTerminal_init(UART_HandleTypeDef *huart);
RET uartTerminal_send(uint8_t data);
uint8_t uartTerminal_recv();
RET uartTerminal_recvTry(uint8_t *data);

#endif /* UARTTERMINAL_UARTTERMINAL_H_ */

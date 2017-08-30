/*
 * common.h
 *
 *  Created on: 2017/08/25
 *      Author: take-iwiw
 */

#ifndef COMMON_H_
#define COMMON_H_

typedef uint32_t RET;

#define RET_OK           0x00000000
#define RET_NO_DATA      0x00000001
#define RET_DO_NOTHING   0x00000002
#define RET_ERR          0x80000001
#define RET_ERR_OF       0x80000002
#define RET_ERR_TIMEOUT  0x80000004
#define RET_ERR_STATUS   0x80000008
#define RET_ERR_PARAM    0x80000010
#define RET_ERR_FILE     0x80000020
#define RET_ERR_MEMORY   0x80000040


#endif /* COMMON_H_ */

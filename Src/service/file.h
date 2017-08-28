/*
 * file.h
 *
 *  Created on: 2017/08/27
 *      Author: take-iwiw
 */

#ifndef SERVICE_FILE_H_
#define SERVICE_FILE_H_

RET file_init();
RET file_deinit();
RET file_seekStart(const char* path);
RET file_seekStop();
RET file_seekFileNext(char* filename);
RET file_loadStart(char* filename);
RET file_loadStop();
RET file_load(void* destAddress, uint32_t numByte, uint32_t* p_numByte);
FIL* file_loadGetCurrentFil();

#endif /* SERVICE_FILE_H_ */

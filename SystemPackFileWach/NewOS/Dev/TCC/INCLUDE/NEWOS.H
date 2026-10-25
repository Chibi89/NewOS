/**
 * newos.h - NewOS V1 Software Development Kit (SDK)
 * Copyright (C) Newspoint Software - NewSoft
 */
#ifndef _NEWOS_H_
#define _NEWOS_H_

typedef unsigned long long uint64_t;
typedef unsigned int       uint32_t;
typedef unsigned short     uint16_t;
typedef unsigned char      uint8_t;

// API System Call & GUI Entrypoints
void sys_print(const char* str);
void sys_exit(int code);

#endif // _NEWOS_H_

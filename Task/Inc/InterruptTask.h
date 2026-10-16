#ifndef __INTERRUPTTASK_H__
#define __INTERRUPTTASK_H__

#include <stdint.h>

extern volatile uint16_t GameBallCount;
void HoolleInput_FilterInit(void);
void HoolleInput_Scan1ms(void);

#endif